import re, gzip
src = open("EVDCTRL.h", encoding="utf-8").read().replace("\r", "")
html = re.search(r'R"HTML\((.*)\)HTML"', src, re.S).group(1).encode("utf-8")
gz = gzip.compress(html, 9)
with open("EVDCTRL_GZ.h", "w") as f:
    f.write("#pragma once\n#include <Arduino.h>\n")
    f.write("static const uint8_t EVDCTRL_HTML_GZ[] PROGMEM = {\n")
    for i in range(0, len(gz), 16):
        f.write("  " + ",".join("0x%02x" % b for b in gz[i:i+16]) + ",\n")
    f.write("};\n")
    f.write("static const size_t EVDCTRL_HTML_GZ_LEN = %d;\n" % len(gz))
print("raw", len(html), "gz", len(gz))