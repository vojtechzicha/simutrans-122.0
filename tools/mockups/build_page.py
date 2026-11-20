# Puts the published page together: page-head.html + page-body.html + page-script.html, with every
# {{name}} replaced by MOCKUP_DIR/out2/name.png as a data URI. Writes MOCKUP_DIR/platforms.html.
import base64, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sim import DATA, HERE

parts = [open(os.path.join(HERE, n), encoding='utf-8').read() for n in ('page-head.html', 'page-body.html', 'page-script.html')]
page = parts[0] + parts[1] + '\n' + parts[2]
page = re.sub(r'\{\{(\w+)\}\}', lambda m: 'data:image/png;base64,' + base64.b64encode(open(os.path.join(DATA, 'out2', m.group(1) + '.png'), 'rb').read()).decode(), page)
out = os.path.join(DATA, 'platforms.html')
open(out, 'w', encoding='utf-8').write(page)
print(out, len(page))
