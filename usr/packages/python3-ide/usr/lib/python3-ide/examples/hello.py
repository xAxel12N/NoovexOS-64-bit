import sys
import platform

print("Hello from Python", platform.python_version(), "on NoovexOS")

name = input("What is your name? ")
print(f"Nice to meet you, {name}!")

squares = [n * n for n in range(1, 11)]
print("Squares:", squares)
print("Sum:", sum(squares))
