#!/bin/bash
  
while IFS= read -r line; do
  echo "$(date) $line"
done

