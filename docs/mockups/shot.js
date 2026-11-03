const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage({ viewport: { width: 1280, height: 800 } });
  for (const n of ['journal', 'wars']) {
    await p.goto('file://' + __dirname + '/' + n + '.html');
    await p.screenshot({ path: __dirname + '/' + n + '.png' });
  }
  await b.close();
})();
