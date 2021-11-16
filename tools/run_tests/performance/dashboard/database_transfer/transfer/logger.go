package transfer

import (
	"log"
	"os"
)

// Logger proves per-table logging.
type Logger struct {
	prefix string
	*log.Logger
}

// NewLogger returns a new Logger.
func NewLogger(tableName string) *Logger {
	prefix := "[" + tableName + "]"
	prefixedLogger := log.New(os.Stderr, prefix+" ", log.Ldate|log.Ltime|log.Lmsgprefix)
	return &Logger{prefix, prefixedLogger}
}

// Errorf adds an "[ERROR]" tag to the log.
func (tl *Logger) Errorf(format string, v ...interface{}) {
	tl.SetPrefix(tl.prefix + "[ERROR] ")
	tl.Printf(format, v...)
	tl.SetPrefix(tl.prefix + " ")
}
