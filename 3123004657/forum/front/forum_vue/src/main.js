import './assets/base.scss'

import VueCookies from 'vue-cookies'
import ElementPlus from 'element-plus'
import 'element-plus/dist/index.css'
import '@/assets/icon/iconfont.css'
import { createApp } from 'vue'
import App from './App.vue'
import router from './router'
import store from './store'
import Verify from './utils/Verify'
import Message from './utils/Message'
import Request from './utils/Request'
import Avatar from './components/Avatar.vue'
import Dialog from './components/Dialog.vue'
import DataList from './components/DataList.vue'
import NoData from './components/NoData.vue'

import VMdEditor from '@kangc/v-md-editor';
import '@kangc/v-md-editor/lib/style/base-editor.css';
import githubTheme from '@kangc/v-md-editor/lib/theme/github.js';
import '@kangc/v-md-editor/lib/theme/style/github.css';
import hljs from 'highlight.js';

VMdEditor.use(githubTheme, {
  Hljs: hljs,
});

const app = createApp(App)
app.use(store)
app.use(router)
app.use(ElementPlus);
app.use(VMdEditor)
app.config.globalProperties.VueCookies = VueCookies;
app.config.globalProperties.Verify = Verify;
app.config.globalProperties.Message = Message;
app.config.globalProperties.Request = Request;
app.config.globalProperties.globalInfo = {
    bodyWidth: 1300,
}
app.component('Dialog', Dialog);
app.component('Avatar', Avatar);
app.component('DataList', DataList);
app.component('NoData', NoData);
app.mount('#app')
