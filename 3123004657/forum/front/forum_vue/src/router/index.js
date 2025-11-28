import { createRouter, createWebHistory } from 'vue-router'


const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path:'/',
      name:'layout',
      component: () => import('@/views/Layout.vue'),
      children : [{
        path:'/',
        name:'articleList',
        component: () => import('@/views/ArticleList.vue'),
      },
      {
        path:'/forum/:boardId',
        name:'一级板块',
        component: () => import('@/views/ArticleList.vue'),
      },
      {
        path:'/user/:userId',
        name:'用户信息',
        component: () => import('@/views/Ucenter.vue'),
      }]
    }
    
  ]
})

export default router
