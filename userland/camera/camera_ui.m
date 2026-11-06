#import <UIKit/UIKit.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>
#include "in2020_camera_user.h"
#include "preview.h"

@interface IN2020CameraPanel : UIViewController
@property(nonatomic,strong) UIImageView *preview;
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) UISegmentedControl *sensor;
@property(nonatomic,strong) UIButton *take;
@property(nonatomic,strong) UIButton *toggle;
@property(nonatomic,assign) BOOL working;
@end
static UIWindow *cameraWindow;
@implementation IN2020CameraPanel
- (void)viewDidLoad {
    [super viewDidLoad];self.view.backgroundColor=UIColor.blackColor;
    UIStackView *stack=[[UIStackView alloc] init];stack.axis=UILayoutConstraintAxisVertical;stack.spacing=12;
    stack.translatesAutoresizingMaskIntoConstraints=NO;[self.view addSubview:stack];
    UILayoutGuide *safe=self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[[stack.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor constant:18],
        [stack.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor constant:-18],
        [stack.topAnchor constraintEqualToAnchor:safe.topAnchor constant:12],
        [stack.bottomAnchor constraintEqualToAnchor:safe.bottomAnchor constant:-12]]];
    UILabel *title=[[UILabel alloc] init];title.text=@"IN2020 相机测试";title.textColor=UIColor.whiteColor;
    title.font=[UIFont boldSystemFontOfSize:24];[stack addArrangedSubview:title];
    self.sensor=[[UISegmentedControl alloc] initWithItems:@[@"前摄 IMX471",@"长焦 S5K3M5"]];
    self.sensor.selectedSegmentIndex=0;[self.sensor addTarget:self action:@selector(change) forControlEvents:UIControlEventValueChanged];
    [stack addArrangedSubview:self.sensor];
    self.preview=[[UIImageView alloc] init];self.preview.contentMode=UIViewContentModeScaleAspectFit;
    [self.preview setContentHuggingPriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisVertical];
    [self.preview setContentCompressionResistancePriority:UILayoutPriorityDefaultLow forAxis:UILayoutConstraintAxisVertical];
    [stack addArrangedSubview:self.preview];
    self.status=[[UILabel alloc] init];self.status.numberOfLines=0;self.status.textColor=UIColor.whiteColor;
    self.status.font=[UIFont systemFontOfSize:15];self.status.text=@"点击取帧。当前为灰度 RAW 单帧预览。";
    [stack addArrangedSubview:self.status];
    self.take=[UIButton buttonWithType:UIButtonTypeSystem];[self.take setTitle:@"取帧 / 刷新" forState:UIControlStateNormal];
    [self.take addTarget:self action:@selector(capture) forControlEvents:UIControlEventTouchUpInside];[stack addArrangedSubview:self.take];
    self.toggle=[UIButton buttonWithType:UIButtonTypeSystem];[self.toggle setTitle:@"收起测试窗口" forState:UIControlStateNormal];
    [self.toggle addTarget:self action:@selector(togglePanel) forControlEvents:UIControlEventTouchUpInside];[stack addArrangedSubview:self.toggle];
}
- (void)togglePanel {
    BOOL compact=cameraWindow.frame.size.height<150;
    cameraWindow.frame=compact?UIScreen.mainScreen.bounds:CGRectMake(20,80,180,64);
    for(UIView *v in self.view.subviews)v.hidden=!compact;
    if(!compact) {
        UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];button.tag=2020;button.frame=self.view.bounds;
        button.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
        [button setTitle:@"打开相机测试" forState:UIControlStateNormal];
        [button addTarget:self action:@selector(togglePanel) forControlEvents:UIControlEventTouchUpInside];[self.view addSubview:button];
    } else [[self.view viewWithTag:2020] removeFromSuperview];
}
- (void)change {self.preview.image=nil;self.status.text=@"已切换镜头，请点击取帧。";}
- (void)capture {
    if(self.working)return;
    self.working=YES;self.take.enabled=NO;self.sensor.enabled=NO;self.preview.image=nil;
    self.status.text=@"正在取帧，请保持镜头朝向明亮场景…";
    uint32_t sensor=(uint32_t)self.sensor.selectedSegmentIndex+1;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        int error=0;int fd=open("/dev/in2020-camera",O_RDONLY|O_CLOEXEC);
        uint8_t *raw=NULL,*pixels=NULL;UIImage *image=nil;uint32_t width=0,height=0;uint64_t stamp=0;
        if(fd<0)error=errno;
        if(!error && ioctl(fd,IN2020_CAMERA_SNAPSHOT,&sensor)<0)error=errno;
        struct i20_header h={0};
        if(!error && !i20_read_all(fd,&h,sizeof(h)))error=EIO;
        if(!error && !i20_header_valid(&h))error=EINVAL;
        size_t size=(size_t)h.stride*h.height;
        if(!error) {raw=malloc(size);if(!raw)error=ENOMEM;}
        if(!error && (!i20_read_all(fd,raw,size) || i20_crc(raw,size)!=h.crc))error=EIO;
        if(fd>=0)close(fd);
        if(!error) {
            width=h.width/4;height=h.height/4;stamp=h.time;
            pixels=malloc((size_t)width*height);if(!pixels)error=ENOMEM;
            if(!error) {
                i20_preview(raw,h.width,h.height,h.stride,pixels);
                NSData *data=[NSData dataWithBytes:pixels length:(NSUInteger)width*height];
                CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)data);
                CGColorSpaceRef space=CGColorSpaceCreateDeviceGray();
                CGImageRef cg=CGImageCreate(width,height,8,8,width,space,(CGBitmapInfo)kCGImageAlphaNone,provider,NULL,false,kCGRenderingIntentDefault);
                if(cg)image=[UIImage imageWithCGImage:cg];else error=ENOMEM;
                if(cg)CGImageRelease(cg);CGColorSpaceRelease(space);CGDataProviderRelease(provider);
            }
        }
        free(raw);free(pixels);
        NSLog(@"IN2020 camera UI sensor=%u error=%d frame=%llu",sensor,error,(unsigned long long)stamp);
        dispatch_async(dispatch_get_main_queue(), ^{
            self.working=NO;self.take.enabled=error==0;self.sensor.enabled=error==0;self.preview.image=image;
            self.status.text=error?[NSString stringWithFormat:@"取帧失败 (%d)。请保留日志并重新启动测试镜像。",error]:
                [NSString stringWithFormat:@"%@ · %u×%u · CRC 已校验\n灰度单帧，点击刷新；亮度为显示拉伸。",sensor==1?@"前摄":@"长焦",h.width,h.height];
        });
    });
}
@end
__attribute__((constructor)) static void camera_start(void) {
    if(![NSProcessInfo.processInfo.processName isEqualToString:@"SpringBoard"])return;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW,30*NSEC_PER_SEC),dispatch_get_main_queue(), ^{
        cameraWindow=[[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
        for(UIScene *scene in UIApplication.sharedApplication.connectedScenes)if([scene isKindOfClass:UIWindowScene.class]) {
            cameraWindow.windowScene=(UIWindowScene *)scene;break;
        }
        cameraWindow.windowLevel=UIWindowLevelAlert+1;
        cameraWindow.rootViewController=[[IN2020CameraPanel alloc] init];cameraWindow.hidden=NO;
        NSLog(@"IN2020 camera UI window visible; native front/tele snapshot entry");
    });
}
