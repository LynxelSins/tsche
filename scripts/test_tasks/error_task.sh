#!/bin/bash
# always fails with exit code 3
echo "error_task: working..."
sleep 1
echo "error_task: something went wrong" >&2
exit 3
