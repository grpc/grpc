package transfer

import (
	"context"
	"fmt"
	"log"
	"strings"

	"cloud.google.com/go/bigquery"
	"google.golang.org/api/iterator"
)

// BigQueryClient interacts with an instance of BigQuery.
type BigQueryClient struct {
	bqClient *bigquery.Client
	ctx      context.Context
}

// BigQuerySchema is a map of column names to BigQuery datatypes.
type BigQuerySchema struct {
	schema map[string]string
}

// NewBigQueryClient creates a new BigQueryClient.
func NewBigQueryClient(ctx context.Context, config BigQueryConfig) (*BigQueryClient, error) {
	bq, err := bigquery.NewClient(ctx, config.ProjectID)
	if err != nil {
		return nil, err
	}
	bqc := &BigQueryClient{bq, ctx}
	if err != nil {
		return nil, err
	}
	return bqc, nil
}

// ListTables lists all tables in the BigQuery instance.
func (bqc *BigQueryClient) ListTables() error {
	it := bqc.bqClient.Datasets(bqc.ctx)
	for {
		dataset, err := it.Next()
		if err == iterator.Done {
			break
		}
		if err != nil {
			return err
		}
		log.Println(dataset.DatasetID)
	}
	return nil
}

// GetDataAfterDatetime gets all data after the specified datetime.
func (bqc *BigQueryClient) GetDataAfterDatetime(dataset, table, dateField, datetime string, bqSchema *BigQuerySchema) (*bigquery.RowIterator, error) {
	selectStr := ""
	for columnName, dataType := range bqSchema.schema {
		var selectCol string
		if strings.Contains(dataType, "STRUCT") {
			selectCol = fmt.Sprintf("TO_JSON_STRING(%s) AS %s", columnName, columnName)
		} else {
			selectCol = fmt.Sprintf("%s", columnName)
		}

		if selectStr == "" {
			selectStr = selectCol
		} else {
			selectStr = fmt.Sprintf("%s, %s", selectStr, selectCol)
		}
	}
	querySQL := fmt.Sprintf("SELECT %s FROM `%s.%s` WHERE %s > \"%s\";", selectStr, dataset, table, dateField, datetime)
	if datetime == "" {
		querySQL = fmt.Sprintf("SELECT %s FROM `%s.%s`;", selectStr, dataset, table)
	}

	return bqc.bqClient.Query(querySQL).Read(bqc.ctx)
}

// GetTableSchema gets the schema for the specified BigQuery table.
// It returns a map whose keys are column names and values are BigQuery types.
func (bqc *BigQueryClient) GetTableSchema(dataset, table string) (*BigQuerySchema, error) {
	bqSchema := &BigQuerySchema{make(map[string]string)}

	colQuery := fmt.Sprintf("SELECT column_name, data_type FROM `%s.INFORMATION_SCHEMA.COLUMNS` WHERE table_name=\"%s\"", dataset, table)
	query := bqc.bqClient.Query(colQuery)
	rows, err := query.Read(bqc.ctx)
	if err != nil {
		return nil, err
	}
	for {
		var row rowSchema
		err := rows.Next(&row)
		if err == iterator.Done {
			break
		}
		if err != nil {
			return nil, err
		}
		bqSchema.schema[row.ColumnName] = row.DataType
	}
	return bqSchema, nil
}

type rowSchema struct {
	ColumnName string `bigquery:"column_name"`
	DataType   string `bigquery:"data_type"`
}
