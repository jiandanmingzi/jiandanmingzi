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
        path:'/post/:articleId',
        name:'文章详情',
        component: () => import('@/views/ArticleDetail.vue'),
      },
      {
        path:'/post/:articleId',
        name:'文章详情',
        component: () => import('@/views/ArticleDetail.vue'),
      },
      {
        path:'/newPost',
        name:'发布文章',
        component: () => import('@/views/NewPost.vue'),
      },
      {
        path:'/editPost/:articleId',
        name:'编辑文章',
        component: () => import('@/views/NewPost.vue'),
      },{
        path: '/ucenter/:userId',
        name: '用户中心',
        component: () => import('@/views/Ucenter.vue'),
      }]
    }
    
  ]
})

export default router
