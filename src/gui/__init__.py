import sys


def main():
    try:
        from .main_window import main as _main
    except ImportError as e:
        sys.exit(f"{e}\nThe GUI requires additional dependencies: pip install 'mesytec-mcpd[gui]'")
    _main()


__all__ = ["main"]
