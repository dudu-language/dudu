proc = subprocess.run(
    [f"{repo_root}/build/dudu-lsp"],
    input="".join(messages).encode(),
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    # The comprehensive Debug smoke sends more than 100 requests and exercises
    # native indexing. Keep a hard hang guard without treating Debug overhead
    # as a shipped LSP latency budget.
    timeout=30,
    check=True,
)
if proc.stderr:
    raise AssertionError(proc.stderr.decode())

responses = read_lsp_messages(proc.stdout)
