#!/usr/bin/env python3

# Bring in the necessary helpers from the lambda wrapper, note that
# lambda_handler is used by the Lambda framework, even if it's not
# used here
from lambda_helper import lambda_handler, api, lambda_flask
from calc import Calc


@api("")
def main_page(handler):
    with open("template_main.html", "rt", encoding="utf-8") as f:
        temp = f.read()
    temp = temp.split("\n")
    temp = [x.strip() for x in temp]
    temp = "".join(temp)
    temp = temp.replace("lambda", handler.path)
    handler.page = temp


@api("favicon")
def favicon(handler):
    with open("favicon.ico", "rb") as f:
        handler.page = f.read()


@api("calc", method="POST")
def calculate(handler):
    formula = handler.qsp.get("f", "")
    engine = Calc(unserialize=handler.qsp.get("s", ""))
    result = engine.calc(formula)
    result = "<none>" if result is None else result.list_to_string()

    handler.json = {
        "result": str(result),
        "state": engine.serialize(),
    }


if __name__ == "__main__":
    lambda_flask()
