package transfer

import (
	"fmt"
	"io/ioutil"

	"gopkg.in/yaml.v2"
)

// TODO: validate that dateField is actually a datetime

// Config holds the configuration for the transfer, dictated by the YAML file
// or environment variables.
type Config struct {
	*YAMLConfig
}

// NewConfig creates a new Config.
func NewConfig(yamlFile string) (*Config, error) {
	yConfig, err := readYAML(yamlFile)
	if err != nil {
		return nil, fmt.Errorf("Could not read yaml: %s", err)
	}
	err = validateYAMLConfig(yConfig)
	if err != nil {
		return nil, fmt.Errorf("validation error: %s", err)
	}

	config := &Config{yConfig}
	return config, nil
}

func readYAML(yamlFile string) (*YAMLConfig, error) {
	file, err := ioutil.ReadFile(yamlFile)
	if err != nil {
		return nil, err
	}

	var yc YAMLConfig
	err = yaml.Unmarshal(file, &yc)
	if err != nil {
		return nil, err
	}
	return &yc, nil
}

func validateYAMLConfig(yConfig *YAMLConfig) error {
	tableSet := make(map[string]bool)
	datasetSet := make(map[string]bool)

	// Duplicate datasets and table names are not allowed
	for _, dataset := range yConfig.Transfer.Datasets {
		if datasetSet[dataset.Name] {
			return fmt.Errorf("duplicate dataset name found: %s", dataset.Name)
		}
		datasetSet[dataset.Name] = true
		for _, table := range dataset.Tables {
			if tableSet[table.Name] {
				return fmt.Errorf("duplicate table name found: %s", table.Name)
			}
			tableSet[table.Name] = true
		}
	}
	return nil
}

// YAMLConfig stores the configuration of the application.
type YAMLConfig struct {
	BigQuery BigQueryConfig `yaml:"bigQuery"`
	Postgres PostgresConfig `yaml:"postgres"`
	Transfer TransferConfig `yaml:"transfer"`
}

// BigQueryConfig stores configuration needed to connect to the BigQuery
// instance.
type BigQueryConfig struct {
	ProjectID string `yaml:"projectID"`
}

// PostgresConfig stores configuration needed to connect to the PostgreSQL
// instance.
type PostgresConfig struct {
	DbHost string `yaml:"dbHost"`
	DbPort string `yaml:"dbPort"`
	DbUser string `yaml:"dbUser"`
	DbPass string `yaml:"dbPass"`
	DbName string `yaml:"dbName"`
}

// TransferConfig stores configuartion about which BigQuery datasets and tables
// to transfer to PostgreSQL.
type TransferConfig struct {
	Datasets []struct {
		Name   string `yaml:"name"`
		Tables []struct {
			Name      string `yaml:"name"`
			DateField string `yaml:"dateField"`
		} `yaml:"tables"`
	} `yaml:"datasets"`
}
