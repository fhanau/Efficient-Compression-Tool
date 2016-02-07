Efficient Compression Tool
============================

Efficient Compression Tool (or ECT) is a C++ file optimizer. 
It supports PNG, JPEG, GZIP and ZIP files.

Performance
-------------------------
All tests were run on OS X 10.10 using an i5-2500S and GCC 5.1.0.

File: enwik8, 100,000,000 bytes, compressed into gzip format

|  Compressor    |  File Size    | Time       |
|  ----------    |  -----        | ---------- |
|  gzip -9       |  36,475,811B  |      6.2s  |
|  ECT -2        |  35,344,331B  |     20.3s  |
| [zopfli] -i1   |  35,139,225B  |  1m  6.4s  | 
|  ECT -3        |  35,025,807B  |     22.2s  |
|  zopfli -i5    |  35,014,777B  |  1m 56.2s  |
|  zopfli -i15   |  34,987,258B  |  4m  3.2s  |
|  ECT -4        |  34,981,830B  |     27.3s  |
|  ECT -5        |  34,972,561B  |     40.3s  |
|  ECT -6        |  34,947,315B  |     53.7s  |
|  ECT -7        |  34,944,589B  |  1m 19.7s  |
|  ECT -8        |  34,938,466B  |  3m 52.8s  |


[zopfli]: https://github.com/google/zopfli
