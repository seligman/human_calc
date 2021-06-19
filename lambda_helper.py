#!/usr/bin/env python3

import base64
import urllib
import urllib.parse
import html
import json

r"""
Generally, this can be tested with a simple test event like this:
{
    "queryStringParameters": {
        "val": "hello"
    }
}
"""

_path = "/lambda"

class ApiMethod:
    def __init__(self, name, method=None, methods=None):
        self.name = name
        self.func = None
        self.method = None
        if method is not None:
            self.method = method
        elif methods is not None:
            self.method = methods

_apis = {}
def api(*args, **kargs):
    api = ApiMethod(*args, **kargs)
    _apis[api.name] = api

    def real_api(func):
        api.func = func
        def wrapper(*args2, **kwargs):
            return func(*args2, **kwargs)
        return wrapper

    return real_api

class Handler:
    def __init__(self, event, context):
        self.method = event.get("httpMethod", event.get("requestContext", {}).get("http", {}).get("method", "GET"))
        self.path = _path
        self.qsp = None

        if event is not None:
            self.qsp = event.get("queryStringParameters", None)
        if self.qsp is None:
            self.qsp = event.get("body", None)
            if self.qsp is not None:
                if event.get("isBase64Encoded", False):
                    self.qsp = base64.b64decode(self.qsp)
                if isinstance(self.qsp, bytes):
                    self.qsp = self.qsp.decode("utf-8")
                self.qsp = dict(urllib.parse.parse_qsl(self.qsp))

        if self.qsp is None:
            self.qsp = {}

        for key in self.qsp:
            if isinstance(self.qsp[key], list):
                self.qsp[key] = self.qsp[key][0]

        self.qsp_val = str(self.qsp.get("val", ""))
        self.content_type = 'text/html'
        self.page = ''
        self.json = None
        self.headers = {}
        self.return_code = 200

    def run(self):
        api = _apis.get(self.qsp_val, None)
        if api is not None:
            if api.method is not None:
                if isinstance(api.method, str):
                    if api.method.lower() != self.method.lower():
                        api = None
                elif isinstance(api.method, set):
                    if self.method not in api.method:
                        api = None
                else:
                    api = None

        if api is not None:
            api.func(self)
            if self.json is not None:
                self.page = json.dumps(self.json, separators=(',', ':'))
                self.content_type = "text/json"
        else:
            self.page = "Error"


def lambda_handler(event, context):
    global _path
    if event is not None:
        if "requestContext" in event:
            if "path" in event["requestContext"]:
                _path = event["requestContext"]["path"]
            elif "http" in event["requestContext"]:
                if "path" in event["requestContext"]["http"]:
                    _path = event["requestContext"]["http"]["path"]

    handler = Handler(event, context)
    handler.run()

    if handler.content_type is not None:
        handler.headers['Content-Type'] = handler.content_type
    handler.headers['Content-Language'] = "en"
    handler.headers['Access-Control-Allow-Origin'] = '*'
    handler.headers['Access-Control-Allow-Methods'] = "POST, GET, OPTIONS"

    ret = {
        'statusCode': handler.return_code,
    }

    if len(handler.headers) > 0:
        ret['headers'] = handler.headers
    if len(handler.page) > 0:
        ret['body'] = handler.page
        if isinstance(ret['body'], bytes):
            ret['body'] = base64.b64encode(ret['body']).decode("utf-8")
            ret['isBase64Encoded'] = True

    return ret


def dump_exception(value):
    value = html.escape(value)
    value = value.replace("\n", "<br>\n")
    return "ERROR:<br><pre>" + value + "</pre>"


def to_flask(value):
    body = value.get('body', '')
    if value.get('isBase64Encoded', False):
        body = base64.b64decode(body.encode("utf-8"))
    return (
        body, 
        int(value.get('statusCode', '200')), 
        value.get('headers', {}),
    )


def lambda_flask(static_pages=None):
    global BASE_URL
    BASE_URL = "http://127.0.0.1:5000/"

    import sys

    if len(sys.argv) > 1 and sys.argv[1] == "prod":
        use_prod = True
        print("Using production mode")
    else:
        use_prod = False
        print("Using debug mode")
        print("Use 'prod' to enable production mode")

    try:
        import flask
        import traceback
        from werkzeug.debug import DebuggedApplication
    except ImportError:
        print('There was an error running flask!')
        return
    app = flask.Flask(__name__, static_url_path='')

    def get_root():
        try:
            temp = {}
            temp.update(flask.request.args)
            page = to_flask(lambda_handler({"queryStringParameters": temp, "httpMethod": "GET"}, None))
        except:
            if not use_prod:
                raise
            page = dump_exception(traceback.format_exc())
        return page

    app.add_url_rule(_path, 'get-root', get_root)

    def post_root():
        temp = {}
        temp.update(flask.request.form)
        temp.update(flask.request.args)
        try:
            page = to_flask(lambda_handler({"queryStringParameters": temp, "httpMethod": "POST"}, None))
        except:
            if not use_prod:
                raise
            page = dump_exception(traceback.format_exc())
        return page

    app.add_url_rule(_path, 'post-root', post_root, methods=['POST'])

    if static_pages is not None:
        def get_static():
            import os
            fn = flask.request.path[1:]
            fn = fn.split("/")
            fn = os.path.join(*fn)
            mode = "rt"
            if fn.split(".")[-1].lower() in {"png", "jpg", "gif", "pdf"}:
                mode = "rb"
            with open(fn, mode) as f:
                return f.read()

        for cur in static_pages:
            app.add_url_rule("/" + cur, cur, get_static)

    def redirect_page():
        if static_pages is not None:
            return flask.redirect("/" + static_pages[0], code=302)
        else:
            return flask.redirect(_path, code=302)

    app.add_url_rule('/', 'redirect-page', redirect_page)

    if use_prod:
        app.config["ENV"] = "production"
        app.run(debug=False, use_debugger=False, port=5000, host='0.0.0.0')
    else:
        app.config["ENV"] = "development"
        app.debug = True
        app.wsgi_app = DebuggedApplication(app.wsgi_app, evalex=True)
        app.run(debug=True, use_debugger=True, port=5000)


if __name__ == '__main__':
    print("This module is not meant to be run directly")
