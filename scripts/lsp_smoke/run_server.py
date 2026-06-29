proc = subprocess.run(
    [f"{repo_root}/build/dudu-lsp"],
    input="".join(messages).encode(),
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    timeout=10,
    check=True,
)
if proc.stderr:
    raise AssertionError(proc.stderr.decode())

responses = read_lsp_messages(proc.stdout)
