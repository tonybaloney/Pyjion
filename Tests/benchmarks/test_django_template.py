"""Test the performance of the Django template system.
This will have Django generate a 150x150-cell HTML table.
"""
import pyjion
import timeit
import django.conf
from django.template import Context, Template


# 2016-10-10: Python 3.6 takes 380 ms
DEFAULT_SIZE = 100


def bench_django_template(size=100):
    template = Template("""<table>
{% for row in table %}
<tr>{% for col in row %}<td>{{ col|escape }}</td>{% endfor %}</tr>
{% endfor %}
</table>
    """)
    table = [range(size) for _ in range(size)]
    context = Context({"table": table})
    template.render(context)


if __name__ == "__main__":
    django.conf.settings.configure(TEMPLATES=[{
        'BACKEND': 'django.template.backends.django.DjangoTemplates',
    }])
    django.setup()

    print("Django Template took {0} without Pyjion".format(timeit.repeat(bench_django_template, repeat=5, number=1)))
    pyjion.enable()
    pyjion.set_optimization_level(1)
    print("Django Template took {0} with Pyjion".format(timeit.repeat(bench_django_template, repeat=5, number=1)))
    pyjion.disable()
