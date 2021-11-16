package transfer

import (
	"context"
	"fmt"
	"log"
	"os"
	"strings"

	"github.com/jackc/pgx/v4/pgxpool"
)

// PostgresClient interacts with an instance of PostgreSQL.
type PostgresClient struct {
	ctx context.Context
	*pgxpool.Pool
	tables []string
}

// PostgresSchema is a map of column names to Postgres datatypes.
type PostgresSchema struct {
	schema map[string]string
}

// NewPostgresClient creates a new PostgresClient.
func NewPostgresClient(config PostgresConfig) (*PostgresClient, error) {
	var (
		host = config.DbHost
		user = config.DbUser
		pass = config.DbPass
		port = config.DbPort
		name = config.DbName
	)
	dbURI := fmt.Sprintf("host=%s user=%s password=%s port=%s database=%s", host, user, pass, port, name)

	env, _ := os.LookupEnv("ENV")
	if env == "local" {
		host = "127.0.0.1"
		port = "5432"
		dbURI = fmt.Sprintf("postgresql://%s:%s@%s:%s/%s", user, pass, host, port, name)
	}

	ctx := context.Background()
	dbPool, err := pgxpool.Connect(ctx, dbURI)
	if err != nil {
		return nil, fmt.Errorf("sql.Open: %v", err)
	}

	pc := &PostgresClient{ctx, dbPool, nil}

	err = pc.testConnection()
	if err != nil {
		return nil, fmt.Errorf("error testing connection: %v", err)
	}

	tables, err := pc.GetExistingTableNames()
	if err != nil {
		return nil, fmt.Errorf("error getting table names: %v", err)
	}
	pc.tables = tables

	return pc, nil
}

func (pc *PostgresClient) testConnection() error {
	return pc.Ping(pc.ctx)
}

// TableExists returns whether a table with the given name exists.
// Table names are cached when the PostgresClient is initialized.
func (pc *PostgresClient) TableExists(searchTable string) bool {
	for _, table := range pc.tables {
		if table == searchTable {
			return true
		}
	}
	return false
}

// CreateTableFromSchema creates a new table from a PostgresSchema.
func (pc *PostgresClient) CreateTableFromSchema(tableName string, pgSchema *PostgresSchema) error {
	sqlSchema := ""
	for columnName, dataType := range pgSchema.schema {
		if sqlSchema == "" {
			sqlSchema = fmt.Sprintf("%s %s", columnName, dataType)
			continue
		}
		sqlSchema = fmt.Sprintf("%s, %s %s", sqlSchema, columnName, dataType)
	}
	query := fmt.Sprintf(`CREATE TABLE "%s" (%s);`, tableName, sqlSchema)
	log.Printf("Creating Postgres table: %s", query)

	_, err := pc.Exec(pc.ctx, query)
	if err != nil {
		return err
	}
	return nil
}

// GetMostRecentEntry returns the lastest timestamp of an entry.
// If the table is empty, an empty string will be returned.
func (pc *PostgresClient) GetMostRecentEntry(table, datetimeField string) (string, error) {
	datetimeField = JSONDotAccessorToArrowAccessor(datetimeField)
	query := "SELECT " + datetimeField + " as date FROM " + table + " ORDER BY date DESC LIMIT 1;"

	rows, err := pc.Query(pc.ctx, query)
	defer rows.Close()
	if err != nil {
		return "", err
	}
	for rows.Next() {
		var date string
		err := rows.Scan(&date)
		if err != nil {
			return "", err
		}
		return date, nil
	}
	return "", nil
}

// GetExistingTableNames returns a list of public tables.
func (pc *PostgresClient) GetExistingTableNames() ([]string, error) {
	var tableNames []string

	query := "select table_name from information_schema.tables WHERE table_schema='public';"
	rows, err := pc.Query(pc.ctx, query)
	defer rows.Close()
	if err != nil {
		return nil, err
	}
	for rows.Next() {
		var tableName string
		err := rows.Scan(&tableName)
		if err != nil {
			return nil, err
		}
		tableNames = append(tableNames, tableName)
	}

	return tableNames, nil
}

// JSONDotAccessorToArrowAccessor converts StandardSQL's dot operator for
// accessing JSON fields into Postgres's arrow operator.
// Example: `a.b.c` to `a->'b'->>'c'`
func JSONDotAccessorToArrowAccessor(str string) string {
	if !strings.Contains(str, ".") {
		return str
	}
	str = strings.Replace(str, ".", "->'", 1)
	str = strings.ReplaceAll(str, ".", "'->'")
	str += "'"
	lastArrowIdx := strings.LastIndex(str, "->")
	lastArrowIdx++
	str = str[:lastArrowIdx] + ">" + str[lastArrowIdx:]
	return str
}
