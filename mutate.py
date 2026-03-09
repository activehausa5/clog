import random

def generate_junk():
    ops = [
        f"int a_{random.randint(1,99)} = {random.randint(1,50)}; if(a_{random.randint(1,99)} < 0) return 0;",
        f"for(int j=0; j<{random.randint(3,7)}; j++) {{ GetTickCount(); }}",
        f"unsigned long k = GetTickCount() ^ {random.randint(100,999)};"
    ]
    return "\n    ".join(random.sample(ops, 2))

with open("main.cpp", "r") as f:
    content = f.read()

# Replaces the placeholder with fresh junk code
mutated = content.replace("// JUNK_HERE", generate_junk())

with open("main.cpp", "w") as f:
    f.write(mutated)
