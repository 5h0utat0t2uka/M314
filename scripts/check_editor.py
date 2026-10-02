"""Check the same LSP diagnostics Zed receives, without clangd's refactoring self-tests."""

import json
import os
import select
import subprocess
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    binary = json.loads((ROOT / ".zed/settings.json").read_text())["lsp"]["clangd"]["binary"]
    commands = json.loads((ROOT / "build/clangd/compile_commands.json").read_text())
    log_path = ROOT / "build/clangd/lsp-check.log"
    with log_path.open("w") as log:
        process = subprocess.Popen([binary["path"], *binary["arguments"], "--background-index=false"],
                                   cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log)
        stdin, stdout = process.stdin, process.stdout
        # PIPE guarantees both streams exist; keep that invariant explicit for type checking.
        assert stdin is not None and stdout is not None
        buffered = bytearray()

        def send(message: dict[str, Any]) -> None:
            body = json.dumps({"jsonrpc": "2.0", **message}).encode()
            stdin.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
            stdin.flush()

        deadline = time.monotonic() + 90

        def receive() -> dict[str, Any]:
            while True:
                split = buffered.find(b"\r\n\r\n")
                if split >= 0:
                    headers = dict(line.split(b":", 1) for line in bytes(buffered[:split]).split(b"\r\n"))
                    length = int(headers[b"Content-Length"])
                    end = split + 4 + length
                    if len(buffered) >= end:
                        message = json.loads(buffered[split + 4:end])
                        del buffered[:end]
                        return message
                remaining = deadline - time.monotonic()
                if remaining <= 0 or not select.select([stdout], [], [], remaining)[0]:
                    raise RuntimeError(f"clangd timed out; see {log_path}")
                chunk = os.read(stdout.fileno(), 65536)
                if not chunk:
                    raise RuntimeError(f"clangd stopped; see {log_path}")
                buffered.extend(chunk)

        try:
            send({"id": 1, "method": "initialize", "params": {
                "processId": os.getpid(), "rootUri": ROOT.as_uri(), "capabilities": {}}})
            while receive().get("id") != 1:
                pass
            send({"method": "initialized", "params": {}})
            pending = set()
            for entry in commands:
                source = Path(entry["file"])
                pending.add(source.as_uri())
                send({"method": "textDocument/didOpen", "params": {"textDocument": {
                    "uri": source.as_uri(), "languageId": "cpp", "version": 1, "text": source.read_text()}}})
            failed = False
            while pending:
                message = receive()
                if message.get("method") != "textDocument/publishDiagnostics":
                    continue
                params = message["params"]
                if params["uri"] not in pending:
                    continue
                pending.remove(params["uri"])
                errors = [d for d in params["diagnostics"] if d.get("severity") == 1]
                print(f"{params['uri'].rsplit('/', 1)[-1]}: {len(errors)} errors", flush=True)
                for error in errors:
                    print(f"  Line {error['range']['start']['line'] + 1}: {error['message']}", flush=True)
                failed |= bool(errors)
            send({"id": 2, "method": "shutdown", "params": None})
            while receive().get("id") != 2:
                pass
            send({"method": "exit", "params": None})
            process.wait(timeout=5)
            if failed:
                raise SystemExit(1)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
