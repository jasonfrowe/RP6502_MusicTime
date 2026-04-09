#!/bin/bash

# Function to clean names: replaces non-alphanumeric characters with underscores
clean_name() {
    echo "$1" | sed -e 's/[^a-zA-Z0-9]/_/g' -e 's/__*/_/g' -e 's/^_//' -e 's/_$//'
}

# Cap cleaned VGM base filenames so they remain safely under the browser limit.
truncate_vgm_base() {
    printf '%.60s' "$1"
}

# Find all .vgz files recursively
# We use a temporary list to avoid issues if the filesystem changes during iteration
find . -name "*.vgz" -type f > vgz_list.tmp

while IFS= read -r f; do
    original_f="$f"

    # Strip leading ./ for easier processing
    rel_f=${original_f#./}

    # Process each path segment (directories and filename)
    IFS='/' read -ra ADDR <<< "$rel_f"
    new_path="."

    for (( i=0; i<${#ADDR[@]}; i++ )); do
        part="${ADDR[$i]}"
        if [ $i -eq $(( ${#ADDR[@]} - 1 )) ]; then
            # It's the filename: remove extension, clean, add .vgm
            base="${part%.vgz}"
            clean_part=$(clean_name "$base")
            clean_part=$(truncate_vgm_base "$clean_part")
            new_path="$new_path/$clean_part.vgm"
        else
            # It's a directory: clean it
            clean_part=$(clean_name "$part")
            new_path="$new_path/$clean_part"
        fi
    done

    # Create the new directory structure
    mkdir -p "$(dirname "$new_path")"

    # Decompress and delete the original
    if gunzip -c "$original_f" > "$new_path" 2>/dev/null; then
        rm "$original_f"
        echo "Done: $new_path"
    else
        echo "Error processing: $original_f"
    fi
done < vgz_list.tmp

rm vgz_list.tmp

# Optional: remove empty directories left behind
find . -type d -empty -delete
