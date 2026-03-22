## Sample Ruby file for metacall-parser testing

require_relative "utils"

def greet(name)
  "Hello, #{name}!"
end

def add(a, b)
  a + b
end

class Calculator
  def add(x, y)
    x + y
  end

  def multiply(x, y)
    x * y
  end
end

