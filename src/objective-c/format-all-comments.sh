#!/usr/bin/bash

find . -type f -name "*.h" ! -path "*/Pods/*" ! -path "./generated_libraries/*" ! -path "./examples/*" ! -path "./tests/*" | xargs ./change-comments.py
