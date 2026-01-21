import re

Import("env")

env_name = env.get("PIOENV", "")
match = re.search(r"Lighthouse_sx126[28]_(\d+)", env_name)
if match:
    env.Append(CPPDEFINES=[("LIGHTHOUSE_NUMBER", match.group(1))])
