const { chromium } = require('playwright');
(async () => { const b = await chromium.launch(); const p = await b.newPage({ viewport: { width: 1600, height: 900 } });
  await p.goto('file://' + __dirname + '/court_hall.html'); await p.screenshot({ path: __dirname + '/court_hall.png' }); await b.close(); })();
