Efficient Compression Tool
============================

Efficient Compression Tool (or ECT) is a C++ file optimizer. 
It supports PNG, JPEG, GZIP and ZIP files.

Performance(v0.6)
-------------------------
All tests were run on OS X 10.11 using an i5-2500S and clang.

File: enwik8, 100,000,000 bytes, compressed into gzip format

|  Compressor    |  File Size    | Time       |
|  ----------    |  -----        | ---------- |
|  gzip -9       |  36,475,811B  |      6.2s  |
| [zopfli] -i1   |  35,139,225B  |  1m  6.4s  | 
|  ECT -2        |  35,019,083B  |     16.9s  |
|  zopfli -i5    |  35,014,777B  |  1m 56.2s  |
|  ECT -3        |  35,012,378B  |     18.3s  |
|  zopfli -i15   |  34,987,258B  |  4m  3.2s  |
|  ECT -4        |  34,964,155B  |     22.5s  |
|  ECT -5        |  34,942,811B  |     28.9s  |
|  ECT -6        |  34,940,747B  |     49.7s  |
|  ECT -7        |  34,938,252B  |  1m  7.9s  |
|  ECT -8        |  34,937,694B  |  3m 54.8s  |


[zopfli]: https://github.com/google/zopfli
