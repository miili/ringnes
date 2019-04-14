# ringnes
![Python3](https://img.shields.io/badge/python-3.5-brightgreen.svg)

_Overwriting ringbuffer for Python_

A simple overwriting ringbuffer for Python, providing a continous strip of virtual memory over the ring trough Python C-API using mmap back magic.
The `put` method is thread safe.

More from the ringnes brewery: https://ringnes.no/

# ToDo

A consumer `get` is not implemented at the moment. Please feel free to contribute!

# Example

```python
from ringnes import Ringbuffer
import numpy as num

# Capacity in bytes
cap = 4096 * 100
ring = Ringbuffer(capacity=cap)

data = num.arange(1002, dtype=num.int32)
for _ in range(20):
    ring.put(memoryview(data))


receive = num.frombuffer(ring, dtype=num.int32)[::-1]
```

# Installation

Python development headers are required, that's it.

```sh
sudo apt-get install python3-dev
pip3 install git+https://github.com/miili/ringnes.git
```


# Resources

https://nbviewer.jupyter.org/url/jakevdp.github.com/downloads/notebooks/BufferProtocol.ipynb

https://lo.calho.st/quick-hacks/employing-black-magic-in-the-linux-page-table/

https://github.com/le1ca/toy-queue/
