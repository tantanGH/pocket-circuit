import os
import sys
import zipfile
import argparse

def main():
    # Set up argument parser
    parser = argparse.ArgumentParser(
        description="Create a plain, X68000-compatible ZIP file using only standard libraries."
    )
    # -j option (junk paths)
    parser.add_argument("-j", "--junk-paths", action="store_true", 
                        help="Junk the path; store just the name of a saved file.")
    # Output ZIP filename
    parser.add_argument("output_zip", help="Name of the output ZIP file")
    # Input files (one or more required)
    parser.add_argument("files", nargs="+", help="Files to include in the ZIP archive")
    
    args = parser.parse_args()

    # Disable ZIP64 (allowZip64=False) for retro-PC compatibility. 
    # Use standard Deflate compression (ZIP_DEFLATED).
    with zipfile.ZipFile(args.output_zip, 'w', zipfile.ZIP_DEFLATED, allowZip64=False) as zipf:
        for file_path in args.files:
            if not os.path.exists(file_path):
                print(f"Error: File not found: {file_path}", file=sys.stderr)
                sys.exit(1)
                
            if os.path.isdir(file_path):
                print(f"Error: Directories are not supported: {file_path}", file=sys.stderr)
                sys.exit(1)

            # --- Determine the internal archive path ---
            if args.junk_paths:
                # -j enabled: Store just the filename (flattened structure)
                archive_name = os.path.basename(file_path)
            else:
                # -j disabled: Maintain the relative path structure.
                # Remove leading './' to prevent issues on Human68k.
                archive_name = os.path.normpath(file_path)
                if archive_name.startswith(f".{os.sep}"):
                    archive_name = archive_name[2:]

            # Exclude macOS metadata files
            basename = os.path.basename(archive_name)
            if basename == '.DS_Store' or basename.startswith('._'):
                continue

            # Convert the internal path to Shift_JIS (CP932) for X68000.
            try:
                sjis_name = archive_name.encode('shift_jis').decode('cp932')
            except UnicodeEncodeError:
                print(f"Warning: Cannot convert to Shift_JIS: {archive_name}", file=sys.stderr)
                sjis_name = archive_name

            zipf.write(file_path, sjis_name)
            print(f"adding: {sjis_name}")

if __name__ == "__main__":
    main()