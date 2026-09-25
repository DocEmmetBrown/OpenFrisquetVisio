import hashlib
import os


Import("env")

PROJECT_DIR = env.subst("$PROJECT_DIR")
WEB_DIR = os.path.join(PROJECT_DIR, "web")
OUTPUT_PATH = os.path.join(PROJECT_DIR, "src", "generated", "WebAssets.h")

ASSETS = (
    ("INDEX_HTML", "index.html"),
    ("LOGS_HTML", "logs.html"),
    ("MEMORY_HTML", "memory.html"),
    ("RADIO_LOGS_HTML", "radio-logs.html"),
)


def raw_delimiter(content):
    digest = hashlib.sha1(content.encode("utf-8")).hexdigest().upper()
    for length in range(8, 13):
        delimiter = "OFV" + digest[:length]
        if ")" + delimiter + '"' not in content:
            return delimiter
    raise ValueError("Impossible de créer un délimiteur C++ pour la ressource HTML")


def render_header():
    declarations = []
    for symbol, filename in ASSETS:
        path = os.path.join(WEB_DIR, filename)
        with open(path, "r", encoding="utf-8") as source:
            content = source.read()
        delimiter = raw_delimiter(content)
        declarations.append(
            "static const char "
            + symbol
            + "[] PROGMEM = R\""
            + delimiter
            + "("
            + content
            + ")"
            + delimiter
            + "\";\n"
        )

    return (
        "// Fichier généré automatiquement depuis web/. Ne pas modifier.\n"
        "#pragma once\n\n"
        "#include <Arduino.h>\n\n"
        "namespace WebAssets {\n\n"
        + "\n".join(declarations)
        + "\n}  // namespace WebAssets\n"
    )


def write_if_changed(path, content):
    try:
        with open(path, "r", encoding="utf-8") as current_file:
            if current_file.read() == content:
                return
    except FileNotFoundError:
        pass

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as output:
        output.write(content)


write_if_changed(OUTPUT_PATH, render_header())
