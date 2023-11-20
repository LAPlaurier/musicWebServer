
var like_list = [false, false, false];
var index = 0;

var vm = new Vue({
    el: '#music',
    data: {
      title : '',
      playtitle : '欢迎使用',
      author : '',
      bgImg : '/images/ball.png',
      src : '',
      isPlaying : false,
      isHeart : false,
      rotateDeg : 0,
      videoProgress: 0,
      videoTime: '00:00 / 00:00',
      hovershow: [], //显示歌单列表播放按钮
      nohovershow: [], //隐藏歌单列表播放按钮
      imgUrlFromServer: "default.png",
      base64: '', //图像的base64编码结果
      Show: true,  
      noShow: false,  // true显示确认 取消上传图像
      loginshow: false,  //true 登录后显示登录头像
      nologshow: true,  //true显示登录按钮
      loginpage: false,
      mainpageshow: true,
      registerpage: false,
      music_title: [],  //歌曲名
      music_autor:[], //作者
      music_time: [], //歌曲时长
      userinfo: {  //登录用户信息
        username: '',
        password: ''
      },
      reguserinfo: {  //登录用户信息
        username: '',
        password: ''
      },
      loginsuccess: false,
      loginfailtips: "",
      cover:"/cover/cover.png",
      f:'' //上传头像文件
    },
    // 计算属性，当duration 和 currentTime改变时自动更新
    computed:{
      videoProgressPercent() {
        return `${this.videoProgress * 100}%`;
      },
    },
    methods: {
        registersubmit() {
          this.loginfailtips = ''
          axios.post('log/3CGISQL.cgi', 'username=' + this.reguserinfo.username + '&password=' + this.reguserinfo.password)
          .then(response => {
            console.log(response.data)
            if(response.data=="no"){
              this.loginfailtips = "提示：该用户名被注册."
            }
            if(response.data=="yes"){
              this.registerpage = false
              this.loginpage = true
              this.mainpageshow = false
            }
          })
        },
        registerlink() {
          this.registerpage = true
          this.loginpage = false
          this.mainpageshow = false
          let canvas = this.$refs['imgPreview']
          let context = canvas.getContext('2d')
          si = 50
          context.clearRect(0, 0, si, si)
        },
        //点击登录按钮，向服务器发送post请求 用户名和密码，服务器返回数据库中的歌单信息
        loginsubmit() {
          axios.post('log/2CGISQL.cgi', 'username=' + this.userinfo.username + '&password=' + this.userinfo.password)
          .then(response => {
            if(response.data=="no"){
              this.loginfailtips = "提示：用户名或密码错误，请重试"
            }
            if(response.data!="no"){
              this.loginpage = false
              this.mainpageshow = true
              this.loginshow = true
              this.nologshow = false
              
              //服务器存在用户上传图像
              if(response.data.split('?')[1]=="yes" || response.data.split('?')[1]=="yes!!")
                this.imgUrlFromServer = '/modifyavatar/' + this.userinfo.username + '.png'
              
              //服务器不存在用户上传图像， 使用默认头像
              if(response.data.split('?')[1]=="no"||response.data.split('?')[1]=="no!!")
                this.imgUrlFromServer = "default.png"
              
              infos = []
              infos = response.data.split('?', 3)
              infos = infos[0].split('\r\n')
              
              //解析数据库歌曲信息
              for(let info of infos) {
                musicinfos = info.split('\t', 3)
                if(musicinfos!=''){
                  this.music_title.push(musicinfos[0])
                  this.music_autor.push(musicinfos[1])
                  this.music_time.push(musicinfos[2])
                  this.hovershow.push(false)
                  this.nohovershow.push(true)
                  console.log(musicinfos[0])
                }
              }
              this.title  = this.music_title[0];
              this.playtitle = this.title
              this.author = this.music_autor[0];
              this.cover  = '/cover/' + this.title + '.jpg';
              const src = '/music/' + this.title + '.mp3';
              this.audioInfo.setAttribute("src", src);
            }
          })
        },
        //当点击登入按钮后，主界面将隐藏，然后出现登录界面
        login() {
          this.loginpage = true  //显示登录界面
          this.mainpageshow = false //隐藏主界面
          let canvas = this.$refs['imgPreview']
          let context = canvas.getContext('2d')
          si = 50
          context.clearRect(0, 0, si, si)
        },
        listClick(index) {
            // console.log(index)
        },
        //图像压缩
        compressImg(file) {
          var files
          var fileSize = parseFloat(parseInt(file.size) / 1024 / 1024).toFixed(3)
          console.log(fileSize)
          var read = new FileReader()
          read.readAsDataURL(file)
          return new Promise(function(resolve, reject) {
            read.onload = function(e) {
              var img = new Image()
              img.src = e.target.result
              img.onload = function() {
                // 默认按比例压缩
                var w = this.width
                var h = this.height
                // 生成canvas
                var canvas = document.createElement('canvas')
                var ctx = canvas.getContext('2d')
                var base64
                // 创建属性节点
                
                //缩放图像
                if(w>300) w = w/10
                else if(w<=300&&w>=50) w = w/5;
                if(h>300) h = h/10;
                else if(h<=300&&h>50) h = h/5

                canvas.setAttribute('width', w)
                canvas.setAttribute('height', h)
                ctx.drawImage(this, 0, 0, w, h)
                
                // 0.3为压缩质量
                base64 = canvas.toDataURL(file['type'], 0.3)
                
                // 回调函数返回file的值（将base64编码转成file）
                resolve(base64)
              }
            }
          });
        },
        //从本地文件夹选择图片，作为头像
        chooseImg (event) {
            let file = event.target.files[0]
            //防止上传大图片
            if(file.size >= 5 * 1024 * 1024){
              alert("The photo is too big. Please upload it again(less than 5MB)!" )
              return 
            }
            this.f = file
            let reader = new FileReader()
            let img = new Image()
            // 读取图片
            reader.readAsDataURL(file)
            reader.onloadend = (e) => {
                img.src = e.target.result 
                this.base64 = reader.result
            }
            // 预览图片
            let canvas = this.$refs['imgPreview']
            let context = canvas.getContext('2d')
            img.onload = () => {
                si = 40
                canvas.width = si
                canvas.height = si
                // 清空canvas
                context.clearRect(0, 0, si, si)
                this.Show = false
                this.noShow = true
                // 画图
                context.drawImage(img, 0, 0, si, si)
            }
        },


        // 上传文件
        uploadMP3File(event) {
          let file = event.target.files[0];
  
          // 防止上传大文件
          if (file.size >= 10 * 1024 * 1024) {  // 假设限制文件大小为10MB
            alert("The file is too big. Please upload it again(less than 10MB)!");
            return;
          }
          // 判断文件类型是否为mp3
          if (file.type != "audio/mpeg") {
            alert("The file type is not supported. Please upload an mp3 file!");
            return;
          }

          // 读取文件并上传
          let formData = new FormData();
          formData.append('mp3File', file);
          
          fetch('/upload', {
            method: 'POST',
            body: formData
          })
          .then(response => response.json())
          .then(data => console.log(data))
          .catch(error => console.error(error));
        },


        //取消上传头像，并清除预览图像
        nouploadimg(){
            this.Show = true
            this.noShow = false
            // 清除预览图片
            let canvas = this.$refs['imgPreview']
            let context = canvas.getContext('2d')
            si = 50
            context.clearRect(0, 0, si, si)
        },
        // 上传头像
        uploadimg () {
            this.noShow = false
            // console.log(this.base64)
            //压缩上传图像
            a = this.compressImg(this.f)
            a.then((data)=>{
              axios.post('uploadimg', {
                img: "?" + this.userinfo.username + "/" + data
              }).then(response => {
                  this.imgUrlFromServer = "data:image/png;base64," + response.data;
                  console.log(this.imgUrlFromServer)
              })
            })
             
            //不压缩上传
            // axios.post('uploadimg', {
            //   img: "?" + this.userinfo.username + "/" + this.base64
            // }).then(response => {
            //     this.imgUrlFromServer = "data:image/png;base64," + response.data;
            //     console.log(this.imgUrlFromServer)
            // })
            
            let canvas = this.$refs['imgPreview']
            let context = canvas.getContext('2d')
            si = 50
            context.clearRect(0, 0, si, si)
            this.Show = true
        },
        
      //鼠标覆盖 歌单列表显示播放按钮 
      mouseover(ind){
        this.$set(this.hovershow, ind, true)
        this.$set(this.nohovershow, ind, false)
      },
      //鼠标离开 歌单列表隐藏播放按钮 
      mouseleave(ind){
        this.$set(this.hovershow, ind, false)
        this.$set(this.nohovershow, ind, true)
      },

      zan(){
        this.isHeart ^= 1;
        like_list[index] = this.isHeart;
      },

      playMusic(){
        this.audioInfo.play();
        this.isPlaying = true;
        this.startRotate();
      },
      menuplayMusic(ind){
        this.changeSrc(ind)
        this.audioInfo.play();
        this.isPlaying = true;
        this.startRotate();
      },
      


      currentTime(){
        if(this.isPlaying&&this.audioInfo.currentTime==this.audioInfo.duration){
            this.nextMusic()
        }
        return this.transferSecToTime(this.audioInfo.currentTime)
      },

      duration(){
        return this.transferSecToTime(this.audioInfo.duration)
      },

      startRotate(){
        // 定时时间，单位ms
        const rotateDuration = 10;
        // 每次旋转角度
        const rotateStep = 0.04;
        // 保存this指针
        const that = this;
  
        // 设置定时器，自动旋转
        // this.clockNum = setInterval(
        //   function(){
        //     that.rotateDeg += rotateStep;
        //   },rotateDuration
        // );

        this.clockNum = setInterval(() => {
          // 更新旋转角度
          this.rotateDeg += rotateStep;
          // 更新元素的旋转属性
          const element = document.getElementById("my-element");
          if (element) {
            element.style.transform = `rotate(${this.rotateDeg}deg)`;
          }
        }, rotateDuration);
      },
      
      pauseMusic(){
        this.audioInfo.pause();
        this.isPlaying = false;
        this.pauseRotate();
      },

      pauseRotate(){
        clearInterval(this.clockNum);
      },
      
      // 监听音频可以播放的事件
      handleCanPlay(event) {
        this.audioInfo.duration = event.target.duration;
      },

      // 监听音频播放位置发送改变的事件
      handleTimeUpdate() {
        this.videoTime = this.refreshTime();
        this.videoProgress = this.audioInfo.currentTime / this.audioInfo.duration;
      },

      refreshTime() {
        if (!this.audioInfo) {
          return this.transferSecToTime(0)/ this.transferSecToTime(0);
        }
        const currTime = this.audioInfo.currentTime|| 0;
        const duration = this.audioInfo.duration || 0;
        return this.transferSecToTime(currTime)/ this.transferSecToTime(duration);
      },

      setProgress(x) {
        const progressRect = document.getElementsByClassName("process-line")[0].getBoundingClientRect()
        let progressPercent = (x - progressRect.left) / progressRect.width;
        if (progressPercent < 0) {
          progressPercent = 0;
        } else if (progressPercent > 1) {
          progressPercent = 1;
        }
        this.audioInfo.currentTime = this.audioInfo.duration * progressPercent;
      },

      handleProgressClick(event) {
        const clickX = event.clientX;
        this.setProgress(clickX)
      },
  
      transferSecToTime(time) {
        if(time == undefined || isNaN(time)) time = 0;
        let tempTime = Math.floor(time);
        let min = 0;
        let sec = 0;
  
        if (tempTime > 60) {
          min = Math.floor(tempTime / 60);
          sec = tempTime % 60;
        } else {
          sec = tempTime;
        }
        if (min < 10) min = "0" + min;
        if (sec < 10) sec = "0" + sec;
        return min + ":" + sec;
      },

      changeSrc(index) {
        this.title  = this.music_title[index];
        this.playtitle = this.title
        this.author = this.music_autor[index];
        this.cover  = '/cover/' + this.title + '.jpg';
        const src = '/music/' + this.title + '.mp3';
        this.audioInfo.setAttribute("src", src);
        this.pauseRotate();
        if (this.isPlaying) this.playMusic();
      },

      lastMusic(){
        const length = this.music_title.length;
        index = ((index - 1) + length) % length;
        this.changeSrc(index)
        
        this.isHeart = like_list[index];
      },

      nextMusic(){
        const length = this.music_title.length;
        index = (index + 1) % length;
        this.changeSrc(index)
        
        this.isHeart = like_list[index];
      }
    },
    created() {
        this.audioInfo = {}
    },
    mounted() {
        this.audioInfo = this.$refs.v
    }
  });
  
