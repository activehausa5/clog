import random

def generate_junk():
    jid = random.randint(1000, 9999)
    ops = [
        f"int x_{jid} = {random.randint(1,100)}; if(x_{jid} < 0) WriteDebug(\"init_{jid}\");",
        f"unsigned long long t_{jid} = GetTickCount64() + {random.randint(10,500)};",
        f"for(int i_{jid}=0; i_{jid}<2; i_{jid}++) {{ x_{jid} ^= i_{jid}; }}"
    ]
    return "\n    ".join(random.sample(ops, 2))

with open("main.cpp", "r") as f:
    content = f.read()

mutated = content.replace("// JUNK_HERE", generate_junk())

with open("main.cpp", "w") as f:
    f.write(mutated)
