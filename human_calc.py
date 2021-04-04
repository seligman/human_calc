#!/usr/bin/env python3

from calc import Calc

def main():
    print("Human Calc:")
    engine = Calc()
    while True:
        value = input()
        result = engine.calc(value)
        if result is None:
            break
        print(f"= {result.to_string()}")
    print("All done")

if __name__ == "__main__":
    main()
