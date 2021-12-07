"""Test the performance of the Django template system.
This will have Django generate a 150x150-cell HTML table.
"""
import django.conf
from django.template import Context, Template


# 2016-10-10: Python 3.6 takes 380 ms
DEFAULT_SIZE = 100


def bench_django_template(size=100):
    if not django.conf.settings.configured:
        django.conf.settings.configure(TEMPLATES=[{
            'BACKEND': 'django.template.backends.django.DjangoTemplates',
        }])
        django.setup()
    template = Template("""<table>
{% for row in table %}
<tr>{% for col in row %}<td>{{ col|escape }}</td>{% endfor %}</tr>
{% endfor %}
</table>
    """)
    table = [range(size) for _ in range(size)]
    context = Context({"table": table})
    template.render(context)


__benchmarks__ = [(bench_django_template, "django_template", {"level": 2, "pgc": True}, 5)]
