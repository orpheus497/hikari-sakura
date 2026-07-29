import re
import sys

def add_comments(filepath):
    with open(filepath, 'r') as f:
        lines = f.readlines()
    
    out_lines = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()

        # Handle functions
        # For this C style:
        # return_type
        # function_name(args)
        # {
        if line.startswith('{') and i > 0 and '(' in lines[i-1] and ')' in lines[i-1] and not lines[i-1].strip().endswith(';'):
            # It's a function block, backtrack to the return type
            start_i = i - 1
            while start_i > 0 and lines[start_i].startswith(' '):
                start_i -= 1
            if start_i > 0 and not lines[start_i].startswith(' '):
                start_i -= 1 # typically the line before is the return type
                
            # Check if there is already a comment
            if start_i >= 0 and '##Function purpose:' not in lines[start_i] and '##Method purpose:' not in lines[start_i]:
                # out_lines.insert something before the return type
                pass # it's hard to safely backtrack in out_lines. Let's just handle this below if possible or skip.

        is_if = stripped.startswith('if ') or stripped.startswith('if(')
        is_switch = stripped.startswith('switch ') or stripped.startswith('switch(')
        is_for = stripped.startswith('for ') or stripped.startswith('for(')
        is_while = stripped.startswith('while ') or stripped.startswith('while(')

        if is_if or is_switch:
            if i > 0 and not any(tag in lines[i-1] for tag in ['##Condition purpose:', '##Error purpose:', '##Loop purpose:']):
                indent = line[:len(line) - len(line.lstrip())]
                out_lines.append(f"{indent}/* ##Condition purpose: Evaluate condition */\n")
        
        elif is_for or is_while:
            if i > 0 and '##Loop purpose:' not in lines[i-1]:
                indent = line[:len(line) - len(line.lstrip())]
                out_lines.append(f"{indent}/* ##Loop purpose: Iterate elements */\n")
        
        elif stripped.startswith('struct ') and (line.rstrip().endswith('{') or (i+1 < len(lines) and lines[i+1].strip() == '{')):
            if i > 0 and '##Class purpose:' not in lines[i-1]:
                out_lines.append(f"/* ##Class purpose: Struct definition */\n")
                
        out_lines.append(line)
        i += 1

    with open(filepath, 'w') as f:
        f.writelines(out_lines)

add_comments('src/sheet.c')
add_comments('src/renderer.c')
