import axios from "axios";
import { ElLoading } from "element-plus";
import Message from "./message";

const contentTypeForm = "application/x-www-form-urlencoded;charset=UTF-8";
const contentTypeJson = "application/json;charset=UTF-8";

const instance = axios.create({
    baseURL:"http://localhost:8080/api",
    timeout:1*1000
})

//请求前过滤器
let loading = null;
instance.interceptors.request.use(
    (config) => {
    if (config.showLoading){
        loading = ElLoading.service({
            lock: true,
            text: 'Loading',
            background: 'rgba(0, 0, 0, 0.7)',
        })
    }
    return config;
}, (error) => {
    if (config.showLoading && loading){
        loading.close();
    }
    Message.error("请求错误，请稍后重试！");
    return Promise.reject("请求错误");
});

//请求后过滤器
instance.interceptors.response.use(
    (response) => {
        const {showLoading, errorCallback,showError} = response.config;
        if (showLoading && loading){
            loading.close();
        }
        const responseData = response.data;
        if (responseData.code === 200){
            return responseData.data;
        } else {
            const error = new Error(responseData.message || "请求失败，请稍后重试！");
            error.showError = showError;
            if (errorCallback && typeof errorCallback === "function"){
                errorCallback(responseData);
            }
            return Promise.reject(error);
        }
    }, (error) => {
        if(config.showLoading && loading){
            loading.close();
        }
        return Promise.reject(new Error("网络异常"));
    }

);

const Request = (config) => {
    const {url,params,dataType,showLoading=true, errorCallback, showError = true} = config;
    let contentType = contentTypeForm;
    let formData = new FormData();
    for(let key in params){
        formData.append(key, params[key] == undefined ? "" : params[key]);
    }
    if (dataType !== null && dataType === "json"){
        contentType = contentTypeJson;
    }
    let headers = {
        "Content-Type": contentType,
        "X-Requested-With": "XMLHttpRequest"
    }
    return instance.post(url, formData, {
        headers: headers,
        showLoading: showLoading,
        errorCallback: errorCallback,
        showError: showError
    }).catch(error => {
        if (error.showError){
            Message.error(error.message);
        }
        return null;
    })
}

export default Request;