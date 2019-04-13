import sys

from setuptools import setup, Extension

packname = 'toy_queue'

setup(
    name=packname,
    version='0.0',
    license='GPLv3',
    python_requires='!=3.0.*, !=3.1.*, !=3.2.*, <4',
    install_requires=[],
    packages=[packname],
    package_dir={'toy_queue': 'src'},
    ext_modules=[Extension('queue',
        sources=['src/queue.c']
        )]
)


