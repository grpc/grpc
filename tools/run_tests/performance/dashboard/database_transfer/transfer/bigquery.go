package transfer

import (
	"context"
	"fmt"
	"log"
	"strings"

	"cloud.google.com/go/bigquery"
	"github.com/leporo/sqlf"
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
	sqlf.SetDialect(sqlf.PostgreSQL)
	sqlBuilder := sqlf.New("SELECT").From(fmt.Sprintf("%s.%s", dataset, table))
	if datetime != "" {
		sqlBuilder.Where(fmt.Sprintf("%s > '%s'", dateField, datetime))
	}
	for columnName, dataType := range bqSchema.schema {
		if strings.Contains(dataType, "STRUCT") {
			sqlBuilder.Select(fmt.Sprintf("TO_JSON_STRING(%s) AS %s", columnName, columnName))
		} else {
			sqlBuilder.Select(columnName)
		}
	}
	return bqc.bqClient.Query(sqlBuilder.String()).Read(bqc.ctx)
}

// GetTableSchema gets the schema for the specified BigQuery table.
// It returns a map whose keys are column names and values are BigQuery types.
func (bqc *BigQueryClient) GetTableSchema(dataset, table string) (*BigQuerySchema, error) {
	sqlf.SetDialect(sqlf.PostgreSQL)
	sqlBuilder := sqlf.New("SELECT").
		Select("column_name, data_type").
		From(fmt.Sprintf("%s.INFORMATION_SCHEMA.COLUMNS", dataset)).
		Where(fmt.Sprintf("table_name='%s'", table))

	bqSchema := &BigQuerySchema{make(map[string]string)}
	query := bqc.bqClient.Query(sqlBuilder.String())
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
