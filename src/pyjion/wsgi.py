import pyjion


class PyjionWsgiMiddleware:
    """Enables the Pyjion JIT for all WSGI requests."""
    def __init__(self, application):
        pyjion.enable()
        self.application = application

    def __call__(self, environ, start_response):
        return self.application(environ, start_response)
