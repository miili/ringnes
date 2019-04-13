#!/usr/bin/env python3
from setuptools import setup, Extension


setup(
    name='ringnes',
    version='0.1',
    description='Fast ringbuffer for Python',
    author='MDM',
    classifiers=[
        "Programming Language :: Python :: 3",
        "Environment :: Other Environment",
        "Intended Audience :: Developers",
        "Topic :: Software Development :: Libraries :: Python Modules",
    ],
    package_dir={
        'ringnes': 'src'
    },
    packages=[
        'ringnes',
    ],
    ext_modules=[
        Extension(
            name='ringnes.ringbuffer',
            sources=['src/ringbuffer.c'])
        ]
)
