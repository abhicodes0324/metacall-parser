"""Sample Python file for metacall-parser testing"""

import os
import sys
from utils import helper

def greet(name):
    """Say hello to name."""
    return f"Hello, {name}!"

def add(a, b):
    return a + b

class Calculator:
    def add(self, x, y):
        return x + y

    def multiply(self, x, y):
        return x * y

class DataProcessor:
    def process(self, data):
        return helper(data)
