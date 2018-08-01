//		
// This file has been generated automatically by MonoDevelop to store outlets and		
// actions made in the Xcode designer. If it is removed, they will be lost.		
// Manual changes to this file may not be handled correctly.		
//		
using Foundation;

namespace HelloworldXamarin.iOS
{
    [Register("ViewController")]
    partial class ViewController
    {
        [Outlet]
        UIKit.UIButton Button { get; set; }

        void ReleaseDesignerOutlets()
        {
            if (Button != null)
            {
                Button.Dispose();
                Button = null;
            }
        }
    }
}
