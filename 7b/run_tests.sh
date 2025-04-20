#!/bin/bash

clang 7b.c -o program

echo "Test 1:"
./program 3 3 4
echo ""
echo ""
echo ""
echo "Test 2:"
./program 2 2 1
echo ""
echo ""
echo ""
echo "Test 3:"
./program 5 5 4 
echo ""
echo ""
echo ""
