import { update } from 'js-md5'
import { createStore } from 'vuex'

export default createStore({
  state: {
    hasLogin: false,
    loginUserInfo: null,
    showLoginDialog: false,
    boardList: [],
    boardId: '0',
    yearList: [],
    yearId: '0',
    roleList: [],
    roleId: '0',
  },
  getters: {
    getHasLogin: (state) => {
      return state.hasLogin
    },
    getLoginUserInfo: (state) => {
      return state.loginUserInfo
    },
    getBoardList: (state) => {
      return state.boardList
    },
    getBoardId: (state) => {
      return state.boardId
    },
    getYearList: (state) => {
      return state.yearList
    },
    getYearId: (state) => {
      return state.yearId
    },
    getRoleList: (state) => {
      return state.roleList
    },
    getRoleId: (state) => {
      return state.roleId
    },
  },
  mutations: {
    updateHasLogin(state, value) {
      state.hasLogin = value
    },
    updateLoginUserInfo(state, userInfo) {
      state.loginUserInfo = userInfo
    },
    showLoginDialog(state, value) {
      state.showLoginDialog = value
    },
    saveBoardList(state, value) {
      state.boardList = value
    },
    saveBoardId(state, value) {
      state.boardId = value
    },
    saveYearList(state, value) {
      state.yearList = value
    },
    saveYearId(state, value) {
      state.yearId = value
    },
    saveRoleList(state, value) {
      state.roleList = value
    },
    saveRoleId(state, value) {
      state.roleId = value
    },
  },
  actions: {},
  modules: {},
})
