import subprocess
import os

build_dir = r'D:\Work\esp32\projects\smart-buddy'
output_file = os.path.join(build_dir, 'build_output.txt')

print("Running build_idf.bat...")
result = subprocess.run(
    ['cmd.exe', '/c', os.path.join(build_dir, 'build_idf.bat')],
    cwd=build_dir,
    capture_output=True,
    text=True,
    timeout=600
)

with open(output_file, 'w', encoding='utf-8', errors='replace') as f:
    f.write(result.stdout)
    f.write(result.stderr)

print(result.stdout[-500:] if len(result.stdout) > 500 else result.stdout)
print(result.stderr[-300:] if len(result.stderr) > 300 else result.stderr)
print(f"\nExit code: {result.returncode}")
