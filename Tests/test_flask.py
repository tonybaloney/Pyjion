import pyjion
from pyjion.wsgi import PyjionWsgiMiddleware

from flask import Flask
app = Flask(__name__)

# Override the app wsgi_app property
app.wsgi_app = PyjionWsgiMiddleware(app.wsgi_app)

@app.route('/')
def hello_world():
    return 'Hello, Flask!'


if __name__ == "__main__":
    pyjion.enable()
    app.run()
