/** Sample JavaScript file for metacall-parser testing */

const fs = require('fs');
const { helper } = require('./utils');

function greet(name) {
  return `Hello, ${name}!`;
}

const add = function(a, b) {
  return a + b;
};

const multiply = (x, y) => x * y;

class Calculator {
  add(x, y) {
    return x + y;
  }

  multiply(x, y) {
    return x * y;
  }
}

class DataProcessor {
  process(data) {
    return helper(data);
  }
}
