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


@api("favicon.ico")
def favicon(handler):
    with open("favicon.ico", "rb") as f:
        handler.page = f.read()
    handler.content_type = "image/x-icon"


@api("apple.png")
def favicon(handler):
    with open("favicon.png", "rb") as f:
        handler.page = f.read()
    handler.content_type = "image/png"


@api("calc", method="POST")
def calculate(handler):
    tz_offset = handler.qsp.get("z", None)
    if tz_offset is not None:
        tz_offset = int(tz_offset)
    formula = handler.qsp.get("f", "")
    state = handler.qsp.get("s", "")
    engine = Calc(unserialize=state, tz_offset=tz_offset)
    result = engine.calc(formula)
    result = "<none>" if result is None else result.list_to_string()

    handler.json = {
        "result": str(result),
        "state": engine.serialize(),
    }


if __name__ == "__main__":
    lambda_flask()
