package main

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"os"

	"github.com/grpc/test-infra/dashboard/database_transfer/transfer"
	_ "github.com/jackc/pgx/v4/stdlib"
)

func main() {
	httpServerPort := os.Getenv("PORT")
	if httpServerPort == "" {
		httpServerPort = "8080"
	}

	config, err := transfer.NewConfig("./config/transfer.yaml")
	if err != nil {
		log.Fatalf("Error getting config: %s", err)
	}

	var (
		postgresConfig = config.Postgres
		bigqueryConfig = config.BigQuery
		transferConfig = config.Transfer
	)

	pgdb, err := transfer.NewPostgresClient(postgresConfig)
	if err != nil {
		log.Fatalf("Error initializing PostgreSQL client: %v", err)
	}
	log.Println("Initialized PostgreSQL client")

	bqdb, err := transfer.NewBigQueryClient(context.Background(), bigqueryConfig)
	if err != nil {
		log.Fatalf("Error initializing BigQuery client: %v", err)
	}
	log.Println("Initialized BigQuery client")

	dbTransfer := transfer.NewTransfer(bqdb, pgdb, &transferConfig)
	finished := make(chan bool)
	go serveHTTP(dbTransfer, httpServerPort, finished)

	<-finished
}

func serveHTTP(dbTransfer *transfer.Transfer, port string, finished chan bool) {
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusOK)
		fmt.Fprintln(w, "Alive\n")
	})
	http.HandleFunc("/run", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintf(w, "Request received\n")
		go dbTransfer.Run()
	})
	http.HandleFunc("/kill", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintln(w, "Server killed\n")
		finished <- true
	})

	log.Printf("Listening on port %s", port)
	if err := http.ListenAndServe(":"+port, nil); err != nil {
		log.Fatal(err)
	}
}
