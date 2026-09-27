import { copyFileSync, existsSync, readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import react from '@vitejs/plugin-react'
import { defineConfig, type Plugin } from 'vite'

const HERE = dirname(fileURLToPath(import.meta.url))

/**
 * 저장소의 구조도(`docs/architecture.html`)를 `/architecture.html`로 내보낸다.
 *
 * **`web/public/`으로 복사해 두지 않는 이유**는 807KB짜리 파일이 저장소에 두 벌
 * 생기기 때문이다. 두 벌이 되는 순간 한쪽만 고쳐지고, 그때 어느 쪽이 맞는지 알 수 없다.
 * 원본은 `docs/`에 하나만 두고 — 문서로도 열어야 하니까 — 개발 서버는 그 자리에서
 * 읽어 주고, 빌드는 결과물에 한 번 복사한다.
 */
function architectureDoc(): Plugin {
  const src = resolve(HERE, '../docs/architecture.html')
  const url = '/architecture.html'
  return {
    name: 'minisor-architecture-doc',
    configureServer(server) {
      server.middlewares.use(url, (_req, res) => {
        if (!existsSync(src)) {
          res.statusCode = 404
          res.end('docs/architecture.html 이 없다')
          return
        }
        res.setHeader('Content-Type', 'text/html; charset=utf-8')
        res.end(readFileSync(src))
      })
    },
    closeBundle() {
      /* 빌드 결과에도 넣는다. 없으면 배포된 화면에서 탭이 빈다 */
      if (existsSync(src)) {
        copyFileSync(src, resolve(HERE, 'dist/architecture.html'))
      }
    },
  }
}

// 채널계(8080)로 넘긴다. 화면과 API가 같은 출처가 되어 CORS가 필요 없다.
// 처음엔 화면이 http://localhost:8080을 직접 불렀는데 채널계에 CORS 설정이 없어,
// 브라우저가 JSON POST의 사전 요청(preflight)에서 막았다(T6-05에서 발견).
// `vite preview`도 이 설정을 그대로 쓴다.
const CHANNEL = 'http://localhost:8080'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react(), architectureDoc()],
  server: {
    proxy: {
      '/api': CHANNEL,
      '/ws': { target: CHANNEL, ws: true },
    },
  },
})
