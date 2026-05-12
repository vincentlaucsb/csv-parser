project = "fastpycsv"
author = "Vincent La"
copyright = "2026, Vincent La"

extensions = [
    "myst_parser",
]

source_suffix = {
    ".md": "markdown",
}

master_doc = "index"
exclude_patterns = [
    "_build",
    "Thumbs.db",
    ".DS_Store",
]

html_theme = "furo"
html_title = "fastpycsv Python Bindings"

myst_heading_anchors = 3
