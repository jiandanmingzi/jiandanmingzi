import './assets/base.scss'

import VueCookies from 'vue-cookies'
import ElementPlus from 'element-plus'
import 'element-plus/dist/index.css'
import '@/assets/icon/iconfont.css'
import { createApp } from 'vue'
import App from './App.vue'
import router from './router'
import Dialog from './components/Dialog.vue'
import Verify from './utils/Verify'

const app = createApp(App)

app.use(router)
app.use(ElementPlus);
app.config.globalProperties.VueCookies = VueCookies;
app.config.globalProperties.globalInfo = {
    bodyWidth: 1300,
}
app.config.globalProperties.Verify = Verify;
app.component('Dialog', Dialog);
app.mount('#app')
