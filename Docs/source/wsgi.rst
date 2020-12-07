Using the WSGI middleware
=========================


Django
------

To use the Pyjion WSGI middleware, wrap the ``get_wsgi_application()`` call with the instantiation of the ``PyjionWsgiMiddleware`` in ``wsgi.py``:

.. code-block:: python

    import os
    from django.core.wsgi import get_wsgi_application
    from pyjion.wsgi import PyjionWsgiMiddleware

    os.environ.setdefault('DJANGO_SETTINGS_MODULE', 'my_application.settings')

    application = PyjionWsgiMiddleware(get_wsgi_application())


Flask
-----

To use the Pyjion WSGI middleware, wrap the existing ``app.wsgi_app`` property with the instantiation of the ``PyjionWsgiMiddleware`` in your ``app.py``:


.. code-block:: python

    from pyjion.wsgi import PyjionWsgiMiddleware

    from flask import Flask
    app = Flask(__name__)

    # Override the app wsgi_app property
    app.wsgi_app = PyjionWsgiMiddleware(app.wsgi_app)

    @app.route('/')
    def hello_world():
        return 'Hello, World!'

