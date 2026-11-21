r"""Set the board stackup to JLC's JLC04161H-7628 (1.6 mm, 4 layers) with black solder mask and
white silkscreen. The colours drive KiCad's 3D view; order the same colours at JLC.

    python stackup.py board.kicad_pcb

KiCad's Python doesn't expose the stackup, so this writes the (stackup ...) block into the
board file's (setup ...) section, replacing any that is there. Close the board in KiCad first.
"""
import re
import sys

STACKUP = '''\t\t(stackup
\t\t\t(layer "F.SilkS" (type "Top Silk Screen") (color "White"))
\t\t\t(layer "F.Paste" (type "Top Solder Paste"))
\t\t\t(layer "F.Mask" (type "Top Solder Mask") (color "Black") (thickness 0.01))
\t\t\t(layer "F.Cu" (type "copper") (thickness 0.035))
\t\t\t(layer "dielectric 1" (type "prepreg") (thickness 0.2104) (material "7628") (epsilon_r 4.4) (loss_tangent 0.02))
\t\t\t(layer "In1.Cu" (type "copper") (thickness 0.0152))
\t\t\t(layer "dielectric 2" (type "core") (thickness 1.065) (material "FR4") (epsilon_r 4.6) (loss_tangent 0.02))
\t\t\t(layer "In2.Cu" (type "copper") (thickness 0.0152))
\t\t\t(layer "dielectric 3" (type "prepreg") (thickness 0.2104) (material "7628") (epsilon_r 4.4) (loss_tangent 0.02))
\t\t\t(layer "B.Cu" (type "copper") (thickness 0.035))
\t\t\t(layer "B.Mask" (type "Bottom Solder Mask") (color "Black") (thickness 0.01))
\t\t\t(layer "B.Paste" (type "Bottom Solder Paste"))
\t\t\t(layer "B.SilkS" (type "Bottom Silk Screen") (color "White"))
\t\t\t(dielectric_constraints no)
\t\t)
'''


def main(pcb):
    s = open(pcb, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    s = re.sub(r'\t\t\(stackup\r?\n.*?\r?\n\t\t\)\r?\n', '', s, count=1, flags=re.S)
    i = s.index('\t(setup') + len('\t(setup')
    i = s.index('\n', i) + 1
    s = s[:i] + STACKUP.replace('\n', nl) + s[i:]
    open(pcb, 'w', encoding='utf-8', newline='').write(s)
    print('stackup written: JLC04161H-7628, black mask, white silkscreen')


if __name__ == '__main__':
    main(sys.argv[1])
