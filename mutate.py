import random

def generate_junk():
    # We generate a unique ID for this specific junk block to avoid "undeclared" errors
    id = random.randint(100, 999)
    ops = [
        f"int val_{id} = {random.randint(1, 100)}; if(val_{id} < 0) {{ GetTickCount(); }}",
        f"unsigned long long t_{id} = GetTickCount64() ^ {random.randint(1000, 9999)};",
        f"for(int i_{id}=0; i_{id}<{random.randint(2, 5)}; i_{id}++) {{ i_{id} += 1; i_{id} -= 1; }}"
    ]
    # Pick 2 unique operations
    return "\n    ".join(random.sample(ops, 2))

with open("main.cpp", "r") as f:
    content = f.read()

# Replace the placeholder with the new stable junk code
mutated = content.replace("// JUNK_HERE", generate_junk())

with open("main.cpp", "w") as f:
    f.write(mutated)
