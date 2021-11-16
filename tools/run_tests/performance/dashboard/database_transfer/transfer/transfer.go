package transfer

import (
	"errors"
	"fmt"
	"log"
	"strings"
	"time"

	"cloud.google.com/go/bigquery"
	"google.golang.org/api/iterator"
)

// Transfer provides functions to transfer data from BigQuery to PostgreSQL.
type Transfer struct {
	bq     *BigQueryClient
	pg     *PostgresClient
	config *TransferConfig
	ready  chan bool
}

// NewTransfer returns a new Transfer.
func NewTransfer(bq *BigQueryClient, pg *PostgresClient, config *TransferConfig) *Transfer {
	transfer := &Transfer{
		bq:     bq,
		pg:     pg,
		config: config,
		ready:  make(chan bool, 1),
	}
	transfer.ready <- true
	return transfer
}

// Run creates a tableTransfer goroutine for each table to be transfered.
func (t *Transfer) Run() {
	select {
	case <-t.ready:
		log.Println("Beginning transfer(s)")
	default:
		log.Println("Transfer(s) already in progress, skipping")
		return
	}

	activeTransfers := 0
	done := make(chan bool)

	for _, dataset := range t.config.Datasets {
		for _, table := range dataset.Tables {
			go t.transferTable(dataset.Name, table.Name, table.DateField, done)
			activeTransfers++
		}
	}
	for activeTransfers > 0 {
		<-done
		activeTransfers--
	}

	log.Println("All transfers complete")
	t.ready <- true
}

// RunContinuously continuously runs Transfer.Run, with sleepTimeInSecs between
// transfers.
func (t *Transfer) RunContinuously(sleepAfterTransferInSecs int) {
	for {
		t.Run()
		log.Printf("Sleeping for %d seconds", sleepAfterTransferInSecs)
		time.Sleep(time.Duration(sleepAfterTransferInSecs) * time.Second)
	}
}

func (t *Transfer) transferTable(bigQueryDataset, tableName, dateField string, done chan bool) {
	logger := NewLogger(tableName)

	// Get the BigQuery table schema
	bqSchema, err := t.bq.GetTableSchema(bigQueryDataset, tableName)
	if err != nil {
		logger.Errorf("Could not get BigQuery table schema: %v", err)
		done <- true
		return
	}

	// Convert BigQuery schema to Postgres schema
	pgSchema, err := t.convertSchema(bqSchema)
	if err != nil {
		logger.Errorf("Could not convert schema: %v", err)
		done <- true
		return
	}

	// Create PostgreSQL table if needed
	err = t.prepareTable(tableName, pgSchema)
	if err != nil {
		logger.Errorf("Could not prepare Postgres table: %v", err)
		done <- true
		return
	}

	// Get rows to transfer
	rows, err := t.getBigQueryRows(bigQueryDataset, tableName, dateField, bqSchema)
	if err != nil {
		logger.Errorf("Could not get data from BigQuery: %v", err)
		done <- true
		return
	}

	// Transfer rows to Postgres
	err = t.transferToPostgres(tableName, pgSchema, rows, logger)
	if err != nil {
		logger.Errorf("Could not transfer one or more rows to Postgres: %v. ", err)
		done <- true
		return
	}

	done <- true
}

func (t *Transfer) convertSchema(bqSchema *BigQuerySchema) (*PostgresSchema, error) {
	pgSchema := &PostgresSchema{make(map[string]string)}
	for columnName, dataType := range bqSchema.schema {
		if strings.Contains(dataType, "STRUCT") {
			pgSchema.schema[columnName] = "JSON"
			continue
		}
		if strings.Contains(dataType, "FLOAT64") {
			pgSchema.schema[columnName] = "DOUBLE PRECISION"
			continue
		}
		if strings.Contains(dataType, "STRING") {
			pgSchema.schema[columnName] = "TEXT"
			continue
		}
		if strings.Contains(dataType, "TIME") {
			pgSchema.schema[columnName] = "TIMESTAMPTZ"
			continue
		}
		pgSchema.schema[columnName] = dataType
	}
	return pgSchema, nil
}

func (t *Transfer) prepareTable(tableName string, pgSchema *PostgresSchema) error {
	tableExists := t.pg.TableExists(tableName)
	if tableExists {
		return nil
	}

	err := t.pg.CreateTableFromSchema(tableName, pgSchema)
	if err != nil {
		return err
	}

	return nil
}

func (t *Transfer) getBigQueryRows(bigQueryDataset, tableName, dateField string, bqSchema *BigQuerySchema) (*bigquery.RowIterator, error) {
	// Get most recent entry from Postgres table
	timestamp, err := t.pg.GetMostRecentEntry(tableName, dateField)
	if err != nil {
		return nil, fmt.Errorf("Could not get most recent Postgres timestamp: %s", err)
	}

	// Get data after this time, or all data if last timestamp doesn't exist
	rows, err := t.bq.GetDataAfterDatetime(bigQueryDataset, tableName, dateField, timestamp, bqSchema)
	if err != nil {
		return nil, err
	}
	return rows, nil
}

func (t *Transfer) transferToPostgres(tableName string, pgSchema *PostgresSchema, rows *bigquery.RowIterator, logger *Logger) error {
	// Begin transaction
	ctx := t.pg.ctx
	tx, err := t.pg.Begin(ctx)
	defer tx.Rollback(ctx)
	if err != nil {
		return errors.New("Could not begin transaction")
	}

	// Transfer rows to Postgres
	rowsPrinted := false
	for {
		row := make(map[string]bigquery.Value)
		err = rows.Next(&row)
		if !rowsPrinted {
			logger.Printf("Rows to transfer: %d", rows.TotalRows)
			rowsPrinted = true
		}
		if err == iterator.Done {
			break
		}
		if err != nil {
			return fmt.Errorf("Big query row error: %s", err)
		}
		insertSQL, err := createInsertSQL(tableName, pgSchema, row)
		if err != nil {
			return fmt.Errorf("Could not construct insert SQL: %s", err)
		}
		_, err = tx.Exec(ctx, insertSQL)
		if err != nil {
			return fmt.Errorf("Transaction exec error: %s, %s", err, insertSQL)
		}
	}

	// Commit transaction
	err = tx.Commit(ctx)
	if err != nil {
		return fmt.Errorf("Transaction commit error: %s", err)
	}
	return nil
}

func createInsertSQL(tableName string, pgSchema *PostgresSchema, row map[string]bigquery.Value) (string, error) {
	columnNames := ""
	valuesString := ""

	for colName := range pgSchema.schema {
		if columnNames == "" {
			columnNames = fmt.Sprintf("%s", colName)
		} else {
			columnNames = fmt.Sprintf("%s, %s", columnNames, colName)
		}

		colValue := row[colName]
		if pgSchema.schema[colName] == "TIMESTAMPTZ" {
			colValue = row[colName].(time.Time).Format(time.RFC3339)
		}
		if pgSchema.schema[colName] == "DOUBLE PRECISION" {
			if colValue == nil {
				// TODO: Change to "NULL" after addding support for nullable columns.
				colValue = "0"
			} else {
				colValue = fmt.Sprintf("%f", row[colName])
			}

		}
		if valuesString == "" {
			valuesString = fmt.Sprintf(`'%s'`, colValue)
		} else {
			valuesString = fmt.Sprintf(`%s, '%s'`, valuesString, colValue)
		}
	}

	insertSQL := fmt.Sprintf(`INSERT INTO "%s" (%s) VALUES (%s);`, tableName, columnNames, valuesString)
	return insertSQL, nil
}
