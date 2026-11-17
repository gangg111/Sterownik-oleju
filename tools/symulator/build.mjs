// Sklada symulator w jeden plik HTML: firmware HEX + avr8js + aplikacja.
//   npm install && npm run build
// Wynik:
//   docs/symulator/symulator.html  - samodzielna strona (otworz w przegladarce, takze na Androidzie)
//   build/artifact.html            - ta sama tresc bez szkieletu dokumentu (do publikacji)
import { build } from 'esbuild';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../..');
const hex = fs.readFileSync(path.join(root, 'firmware/build/sterownik_oleju.hex'), 'utf8');
const res = await build({
  entryPoints: [path.join(here, 'src/app.js')], bundle: true, minify: true, format: 'iife',
  target: 'es2019', write: false, legalComments: 'none',
});
const app = res.outputFiles[0].text.replace(/<\/script/gi, '<\\/script');
const page = fs.readFileSync(path.join(here, 'src/page.html'), 'utf8')
  .replace('/*__HEX__*/', () => hex.trim())
  .replace('/*__APP__*/', () => app);

fs.mkdirSync(path.join(here, 'build'), { recursive: true });
fs.writeFileSync(path.join(here, 'build/artifact.html'), page);
const standalone = `<!doctype html>\n<html lang="pl">\n<head>\n<meta charset="utf-8">\n` +
  `<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n</head>\n<body>\n${page}\n</body>\n</html>\n`;
fs.mkdirSync(path.join(root, 'docs/symulator'), { recursive: true });
fs.writeFileSync(path.join(root, 'docs/symulator/symulator.html'), standalone);
console.log('OK', (standalone.length / 1024).toFixed(0), 'KB');
