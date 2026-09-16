import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'

// 채널계(8080)로 넘긴다. 화면과 API가 같은 출처가 되어 CORS가 필요 없다.
// 처음엔 화면이 http://localhost:8080을 직접 불렀는데 채널계에 CORS 설정이 없어,
// 브라우저가 JSON POST의 사전 요청(preflight)에서 막았다(T6-05에서 발견).
// `vite preview`도 이 설정을 그대로 쓴다.
const CHANNEL = 'http://localhost:8080'

// https://vite.dev/config/
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/api': CHANNEL,
      '/ws': { target: CHANNEL, ws: true },
    },
  },
})
