import axios from 'axios'
import { ElLoading } from 'element-plus'
import Message from './Message'
import store from '@/store'

const contentTypeForm = 'application/x-www-form-urlencoded;charset=UTF-8'
const contentTypeJson = 'application/json;charset=UTF-8'

const instance = axios.create({
  baseURL: '/api',
  timeout: 1 * 1000,
})

//请求前过滤器
let loading = null
instance.interceptors.request.use(
  (config) => {
    if (config.showLoading) {
      loading = ElLoading.service({
        lock: true,
        text: 'Loading',
        background: 'rgba(0, 0, 0, 0.7)',
      })
    }
    return config
  },
  (error) => {
    if (config.showLoading && loading) {
      loading.close()
    }
    Message.error('请求错误，请稍后重试！')
    return Promise.reject('请求错误')
  },
)

//请求后过滤器
instance.interceptors.response.use(
  (response) => {
    const { showLoading, errorCallback, showError } = response.config
    if (showLoading && loading) {
      loading.close()
    }

    const responseData = response.data
    if (responseData.status === 'success') {
      return responseData
    } else if (responseData.status === 'unauthorized' || responseData.status === 401) {
      store.commit('updateLoginUserInfo', null)
      return Promise.reject({
        showError: showError,
        message: '未登录或登录已过期，请重新登录！',
      })
    } else {
      // 其他错误处理
      const error = new Error(responseData.message || '请求失败，请稍后重试！')
      error.showError = showError
      if (errorCallback && typeof errorCallback === 'function') {
        errorCallback(responseData)
      }
      return Promise.reject(error)
    }
  },
  (error) => {
    if (error.config && error.config.showLoading && loading) {
      loading.close()
    }

    // 具体的超时错误处理
    if (error.code === 'ECONNABORTED' && error.message.includes('timeout')) {
      Message.error('请求超时，请检查网络连接或稍后重试！')
    } else if (error.response) {
      // 服务器响应了但状态码不在 2xx 范围
      const status = error.response.status
      switch (status) {
        case 401:
          Message.error('未授权，请重新登录！')
          break
        case 403:
          Message.error('拒绝访问！')
          break
        case 404:
          Message.error('请求地址不存在！')
          break
        case 500:
          Message.error('服务器内部错误！')
          break
        default:
          Message.error('网络异常，请稍后重试！')
      }
    } else if (error.request) {
      // 请求发送了但没有收到响应
      Message.error('网络连接异常，请检查网络！')
    } else {
      // 其他错误
      Message.error('配置错误')
    }

    return Promise.reject(error)
  },
)

const Request = (config) => {
  const {
    url,
    params = {},
    dataType = 'json', // 将默认值设为 'json'
    method = 'post',
    showLoading = true,
    errorCallback,
    showError = true,
  } = config

  let contentType = contentTypeForm
  let requestConfig = {
    method: method.toLowerCase(),
    url: url,
    showLoading: showLoading,
    errorCallback: errorCallback,
    showError: showError,
    headers: {
      'X-Requested-With': 'XMLHttpRequest',
    },
  }

  if (method.toLowerCase() === 'get') {
    requestConfig.params = params
  } else {
    if (dataType === 'json') {
      contentType = contentTypeJson
      requestConfig.data = params
      requestConfig.headers['Content-Type'] = contentType
    } else if (dataType === 'form') {
      // 明确改为 'form'，避免误解
      let formData = new FormData()
      for (let key in params) {
        formData.append(key, params[key] == undefined ? '' : params[key])
      }
      requestConfig.data = formData
      delete requestConfig.headers['Content-Type']
    } else {
      // 可以添加其他格式的处理，或者抛出错
      console.warn(`未知的 dataType: ${dataType}，使用默认 JSON 格式`)
      requestConfig.data = params
      requestConfig.headers['Content-Type'] = contentTypeJson
    }
  }

  return instance(requestConfig).catch((error) => {
    if (error.showError) {
      Message.error(error.message)
    }
    return null
  })
}

export default Request
