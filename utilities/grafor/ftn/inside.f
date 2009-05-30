      subroutine inside(lx,ly,x,y,z,funx,funy)
C...01/09/78
	external funx,funy
      dimension x(lx),y(ly),z(lx,ly)
      dimension xexit(50),yexit(50),f(3),xm(3),ym(3),root(10),xinc(3),y
     *inc(3)
      dimension alfa(2)
       common /drinfn/in,xy,cont,k
      common /drin/ nexits,maxnst,ubx,vbx,uby,vby,xstep1,ystep1,
     *nroots,root,alfa,sx,sy,ks1,s2,imark,s0
      if(nroots.eq.0)
     -go to 125
      do 1 n=1,nroots
      if(in.eq.0)
     -go to 2
      x1=xy
      y1=root(n)
      go to 3
2     x1=root(n)
      y1=xy
3     if(nexits.eq.0)
     -go to 12
      do 4 i=1,nexits
      if(abs(x1-xexit(i)).ge.alfa(1)) go to 4
      if(abs(y1-yexit(i)).lt.alfa(2))
     -go to  1
4     continue
12    call tmf(funx(x1,y1),funy(x1,y1),xf,yf)
      call move(xf,yf,0)
      if(imark.ne.1)
     -go to 538
      call number(xf,yf-.15*s2,.3*s2,s0,0,0)
538   continue
      xinc(1)=xstep1
      yinc(1)=ystep1
      do 5 nstep=1,maxnst
      if(xinc(1).eq.0.)
     -go to 106
      if(yinc(1).eq.0.)
     -go to 107
      xinc(2)=xinc(1)
      yinc(3)=yinc(1)
      yinc(2)=0.
      xinc(3)=0.
      go to 7
106   xinc(2)=sx
      xinc(3)=-sx
      yinc(2)=yinc(1)
      yinc(3)=yinc(1)
      go to 7
107   yinc(2)=sy
      yinc(3)=-sy
      xinc(2)=xinc(1)
      xinc(3)=xinc(1)
7     do 8 i=1,3
      xm(i)=x1+xinc(i)
8     ym(i)=y1+yinc(i)
      call itplbv(lx,ly,x,y,z,3,xm,ym,f)
      fmin=abs(f(1)-cont)
      do 9 i=2,3
      temp=abs(f(i)-cont)
      if(temp.ge.fmin)
     -go to 9
      fmin=temp
      xinc(1)=xinc(i)
      yinc(1)=yinc(i)
9     continue
      if(imark.ne.1)
     -go to 20
      if(nstep-ks1) 987,802,20
802   x1=x1+xinc(1)
      y1=y1+yinc(1)
      call tmf(funx(x1,y1),funy(x1,y1),xf,yf)
      call move(xf,yf,0)
      go to 5
20    continue
      x1=x1+xinc(1)
      y1=y1+yinc(1)
      call tmf(funx(x1,y1),funy(x1,y1),xf,yf)
      call move(xf,yf,1)
      if(x1.ge.ubx.and.x1.le.vbx.and.y1.ge.uby.and.y1.le.vby)
     -go to 5
      nexits=nexits+1
      xexit(nexits)=x1-xinc(1)
      yexit(nexits)=y1-yinc(1)
      go to 1
987   x1=x1+xinc(1)
      y1=y1+yinc(1)
5     continue
1     continue
125   imark=0
      return
      end
